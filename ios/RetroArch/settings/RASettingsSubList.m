/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2013 - Jason Fetters
 * 
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#import <objc/runtime.h>
#import "settings.h"

static const char* const SETTINGID = "SETTING";

@implementation RASettingsSubList
{
   NSArray* settings;
};

- (id)initWithSettings:(NSArray*)values title:(NSString*)title
{
   self = [super initWithStyle:UITableViewStyleGrouped];
   settings = values;
  
   [self setTitle:title];
   return self;
}

- (void)handleCustomAction:(NSString*)action
{

}

- (void)writeSettings:(NSArray*)settingList toConfig:(config_file_t*)config
{
   if (!config)
      return;

   NSArray* list = settingList ? settingList : settings;

   for (int i = 0; i != [list count]; i ++)
   {
      NSArray* group = [list objectAtIndex:i];
   
      for (int j = 1; j < [group count]; j ++)
      {
         RASettingData* setting = [group objectAtIndex:j];
         
         switch (setting.type)
         {         
            case GroupSetting:
               [self writeSettings:setting.subValues toConfig:config];
               break;
               
            case FileListSetting:
               if ([setting.value length] > 0)
                  config_set_string(config, [setting.name UTF8String], [[setting.path stringByAppendingPathComponent:setting.value] UTF8String]);
               else
                  config_set_string(config, [setting.name UTF8String], "");
               break;

            case ButtonSetting:
               if (setting.msubValues[0])
                  config_set_string(config, [setting.name UTF8String], [setting.msubValues[0] UTF8String]);
               if (setting.msubValues[1])
                  config_set_string(config, [[setting.name stringByAppendingString:@"_btn"] UTF8String], [setting.msubValues[1] UTF8String]);
               if (setting.msubValues[2])
                  config_set_string(config, [[setting.name stringByAppendingString:@"_axis"] UTF8String], [setting.msubValues[2] UTF8String]);
               break;

            case AspectSetting:
               config_set_string(config, "video_force_aspect", [@"Fill Screen" isEqualToString:setting.value] ? "false" : "true");
               config_set_string(config, "video_aspect_ratio_auto", [@"Game Aspect" isEqualToString:setting.value] ? "true" : "false");
               config_set_string(config, "video_aspect_ratio", "-1.0");
               if([@"4:3" isEqualToString:setting.value])
                  config_set_string(config, "video_aspect_ratio", "1.33333333");
               else if([@"16:9" isEqualToString:setting.value])
                  config_set_string(config, "video_aspect_ratio", "1.77777777");
               break;

            case CustomAction:
               break;

            default:
               config_set_string(config, [setting.name UTF8String], [setting.value UTF8String]);
               break;
         }
      }
   }
}

- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
   RASettingData* setting = [[settings objectAtIndex:indexPath.section] objectAtIndex:indexPath.row + 1];
   
   switch (setting.type)
   {
      case EnumerationSetting:
      case FileListSetting:
      case AspectSetting:
         [[RetroArch_iOS get] pushViewController:[[RASettingEnumerationList alloc] initWithSetting:setting fromTable:(UITableView*)self.view] animated:YES];
         break;
         
      case ButtonSetting:
         (void)[[RAButtonGetter alloc] initWithSetting:setting fromTable:(UITableView*)self.view];
         break;
         
      case GroupSetting:
         [[RetroArch_iOS get] pushViewController:[[RASettingsSubList alloc] initWithSettings:setting.subValues title:setting.label] animated:YES];
         break;
         
      case CustomAction:
         [self handleCustomAction:setting.label];
         break;
         
      default:
         break;
   }
}

- (void)handleBooleanSwitch:(UISwitch*)swt
{
   RASettingData* setting = objc_getAssociatedObject(swt, SETTINGID);
   setting.value = (swt.on ? @"true" : @"false");
}

- (void)handleSlider:(UISlider*)sld
{
   RASettingData* setting = objc_getAssociatedObject(sld, SETTINGID);
   setting.value = [NSString stringWithFormat:@"%f", sld.value];
}

- (UITableViewCell*)tableView:(UITableView*)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
   RASettingData* setting = [[settings objectAtIndex:indexPath.section] objectAtIndex:indexPath.row + 1];
  
   UITableViewCell* cell = nil;

   switch (setting.type)
   {
      case BooleanSetting:
      {
         cell = [self.tableView dequeueReusableCellWithIdentifier:@"boolean"];

         if (cell == nil)
         {
            cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:@"boolean"];
         
            UISwitch* accessory = [[UISwitch alloc] init];
            [accessory addTarget:self action:@selector(handleBooleanSwitch:) forControlEvents:UIControlEventValueChanged];
            cell.accessoryView = accessory;
            
            [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
         }
      
         UISwitch* swt = (UISwitch*)cell.accessoryView;
         swt.on = [setting.value isEqualToString:@"true"];
         objc_setAssociatedObject(swt, SETTINGID, setting, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
      }
      break;

      case RangeSetting:
      {
         cell = [self.tableView dequeueReusableCellWithIdentifier:@"range"];
         
         if (cell == nil)
         {
            cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:@"range"];

            UISlider* accessory = [UISlider new];
            [accessory addTarget:self action:@selector(handleSlider:) forControlEvents:UIControlEventValueChanged];
            accessory.continuous = NO;
            cell.accessoryView = accessory;

            [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
         }
         
         UISlider* sld = (UISlider*)cell.accessoryView;
         sld.minimumValue = setting.rangeMin;
         sld.maximumValue = setting.rangeMax;
         sld.value = [setting.value doubleValue];
         objc_setAssociatedObject(sld, SETTINGID, setting, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
      }
      break;
         
      case EnumerationSetting:
      case FileListSetting:
      case ButtonSetting:
      case CustomAction:
      case AspectSetting:
      {
         cell = [self.tableView dequeueReusableCellWithIdentifier:@"enumeration"];
         cell = cell ? cell : [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:@"enumeration"];
         cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
      }
      break;

      case GroupSetting:
      {
         cell = [self.tableView dequeueReusableCellWithIdentifier:@"group"];
   
         if (cell == nil)
         {
            cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:@"group"];
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
         }
      }
      break;
   }

   cell.textLabel.text = setting.label;
   
   if (setting.type != ButtonSetting)
      cell.detailTextLabel.text = setting.value;
   else
      cell.detailTextLabel.text = [NSString stringWithFormat:@"[KB:%@] [JS:%@] [AX:%@]",
            [setting.msubValues[0] length] ? setting.msubValues[0] : @"N/A",
            [setting.msubValues[1] length] ? setting.msubValues[1] : @"N/A",
            [setting.msubValues[2] length] ? setting.msubValues[2] : @"N/A"];

   return cell;
}

// UITableView item counts
- (NSInteger)numberOfSectionsInTableView:(UITableView*)tableView
{
   return [settings count];
}

- (NSInteger)tableView:(UITableView*)tableView numberOfRowsInSection:(NSInteger)section
{
   return [[settings objectAtIndex:section] count] -1;
}

- (NSString*)tableView:(UITableView*)tableView titleForHeaderInSection:(NSInteger)section
{
   return [[settings objectAtIndex:section] objectAtIndex:0];
}

@end
